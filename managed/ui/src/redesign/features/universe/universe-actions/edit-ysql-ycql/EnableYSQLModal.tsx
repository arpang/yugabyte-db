import React, { FC } from 'react';
import _ from 'lodash';
import { useMutation } from 'react-query';
import { useTranslation } from 'react-i18next';
import { useForm, FormProvider } from 'react-hook-form';
import { toast } from 'react-toastify';
import { Box, Divider, Typography } from '@material-ui/core';
import {
  YBModal,
  YBToggleField,
  YBCheckboxField,
  YBPasswordField,
  YBTooltip
} from '../../../../components';
import { Universe } from '../../universe-form/utils/dto';
import { api } from '../../../../utils/api';
import { getPrimaryCluster } from '../../universe-form/utils/helpers';
import {
  YSQLFormFields,
  YSQLFormPayload,
  RotatePasswordPayload,
  DATABASE_NAME,
  YSQL_USER_NAME
} from './Helper';
import { PASSWORD_REGEX, TOAST_AUTO_DISMISS_INTERVAL } from '../../universe-form/utils/constants';
import { dbSettingStyles } from './DBSettingStyles';
//icons
import InfoMessageIcon from '../../../../assets/info-message.svg';

interface EnableYSQLModalProps {
  open: boolean;
  onClose: () => void;
  universeData: Universe;
}

const TOAST_OPTIONS = { autoClose: TOAST_AUTO_DISMISS_INTERVAL };

export const EnableYSQLModal: FC<EnableYSQLModalProps> = ({ open, onClose, universeData }) => {
  const { t } = useTranslation();
  const classes = dbSettingStyles();
  const { universeDetails, universeUUID } = universeData;
  const primaryCluster = _.cloneDeep(getPrimaryCluster(universeDetails));

  const formMethods = useForm<YSQLFormFields>({
    defaultValues: {
      enableYSQL: primaryCluster?.userIntent?.enableYSQL ?? false,
      enableYSQLAuth: primaryCluster?.userIntent?.enableYSQLAuth ?? false,
      ysqlPassword: '',
      ysqlConfirmPassword: '',
      rotateYSQLPassword: false,
      ysqlCurrentPassword: '',
      ysqlNewPassword: '',
      ysqlConfirmNewPassword: ''
    },
    mode: 'onChange',
    reValidateMode: 'onChange'
  });
  const { control, watch, handleSubmit } = formMethods;

  //watchers
  const enableYSQLValue = watch('enableYSQL');
  const enableYSQLAuthValue = watch('enableYSQLAuth');
  const ysqlPasswordValue = watch('ysqlPassword');
  const rotateYSQLPasswordValue = watch('rotateYSQLPassword');
  const ysqlNewPasswordValue = watch('ysqlNewPassword');

  //Enable or Disable  YSQL and YSQLAuth
  const updateYSQLSettings = useMutation(
    (values: YSQLFormPayload) => {
      return api.updateYSQLSettings(universeUUID, values);
    },
    {
      onSuccess: (response) => {
        console.log(response);
        toast.success(
          t('universeActions.editYSQLSettings.updateSettingsSuccessMsg'),
          TOAST_OPTIONS
        );
        onClose();
      },
      onError: () => {
        toast.error(t('common.genericFailure'), TOAST_OPTIONS);
      }
    }
  );

  //Rotate YSQL Password
  const rotateYSQLPassword = useMutation(
    (values: Partial<RotatePasswordPayload>) => {
      return api.rotateDBPassword(universeUUID, values);
    },
    {
      onSuccess: () => {
        toast.success(t('universeActions.editYSQLSettings.rotatePwdSuccessMsg'), TOAST_OPTIONS);
        onClose();
      },
      onError: () => {
        toast.error(t('common.genericFailure'), TOAST_OPTIONS);
      }
    }
  );

  const handleFormSubmit = handleSubmit(async (values) => {
    if (values.rotateYSQLPassword) {
      //Rotate password if rotateYSQLPassword is true
      const payload: Partial<RotatePasswordPayload> = {
        dbName: DATABASE_NAME,
        ysqlAdminUsername: YSQL_USER_NAME,
        ysqlCurrAdminPassword: values.ysqlCurrentPassword,
        ysqlAdminPassword: values.ysqlNewPassword
      };
      try {
        await rotateYSQLPassword.mutateAsync(payload);
      } catch (e) {
        console.log(e);
      }
    } else {
      //Update YSQL settings if it is turned off
      let payload: YSQLFormPayload = {
        enableYSQL: values.enableYSQL ?? false,
        enableYSQLAuth: values.enableYSQL && values.enableYSQLAuth ? values.enableYSQLAuth : false,
        ysqlPassword: values.ysqlPassword ?? '',
        CommunicationPorts: {
          ysqlServerHttpPort: universeDetails.communicationPorts.ysqlServerHttpPort,
          ysqlServerRpcPort: universeDetails.communicationPorts.ysqlServerRpcPort
        }
      };
      try {
        await updateYSQLSettings.mutateAsync(payload);
      } catch (e) {
        console.log(e);
      }
    }
  });

  return (
    <YBModal
      open={open}
      titleSeparator
      size="sm"
      overrideHeight="auto"
      cancelLabel={t('common.cancel')}
      submitLabel={t('common.applyChanges')}
      title={t('universeActions.editYSQLSettings.modalTitle')}
      onClose={onClose}
      onSubmit={handleFormSubmit}
      submitTestId="EnableYSQLModal-Submit"
      cancelTestId="EnableYSQLModal-Close"
    >
      <FormProvider {...formMethods}>
        <Box
          mb={4}
          mt={2}
          display="flex"
          width="100%"
          flexDirection="column"
          data-testid="EnableYSQLModal-Container"
        >
          <Box className={classes.mainContainer} mb={4}>
            <Box
              display="flex"
              flexDirection="row"
              alignItems="center"
              justifyContent="space-between"
            >
              <Typography variant="h6">
                {t('universeActions.editYSQLSettings.ysqlToggleLabel')}&nbsp;
                <YBTooltip title={t('universeForm.securityConfig.authSettings.enableYSQLHelper')}>
                  <img alt="Info" src={InfoMessageIcon} />
                </YBTooltip>
              </Typography>
              <YBTooltip
                title={
                  primaryCluster?.userIntent?.enableYSQL
                    ? t('universeActions.editYSQLSettings.cannotDisableYSQL')
                    : ''
                }
                placement="top-end"
              >
                <div>
                  <YBToggleField
                    name={'enableYSQL'}
                    inputProps={{
                      'data-testid': 'EnableYSQLModal-Toggle'
                    }}
                    control={control}
                    disabled={rotateYSQLPasswordValue || primaryCluster?.userIntent?.enableYSQL}
                  />
                </div>
              </YBTooltip>
            </Box>
          </Box>
          {enableYSQLValue && (
            <Box className={classes.mainContainer}>
              <Box
                display="flex"
                flexDirection="row"
                alignItems="center"
                justifyContent="space-between"
              >
                <Typography variant="h6">
                  {t('universeActions.editYSQLSettings.authToggleLabel')}&nbsp;
                  <YBTooltip
                    title={t('universeForm.securityConfig.authSettings.enableYSQLAuthHelper')}
                  >
                    <img alt="Info" src={InfoMessageIcon} />
                  </YBTooltip>
                </Typography>
                <YBToggleField
                  name={'enableYSQLAuth'}
                  inputProps={{
                    'data-testid': 'EnableYSQLModal-AuthToggle'
                  }}
                  control={control}
                  disabled={rotateYSQLPasswordValue}
                />
              </Box>
              {!enableYSQLAuthValue && primaryCluster?.userIntent?.enableYSQLAuth && (
                <Box flex={1} mt={2} width="300px">
                  <YBPasswordField
                    rules={{
                      required: t('universeForm.validation.required', {
                        field: t('universeForm.securityConfig.authSettings.ysqlAuthPassword')
                      }) as string
                    }}
                    name={'ysqlPassword'}
                    control={control}
                    fullWidth
                    inputProps={{
                      autoComplete: 'new-password',
                      'data-testid': 'YSQLField-PasswordLabelInput'
                    }}
                    placeholder={t('universeActions.editYSQLSettings.currentPwdToAuth')}
                  />
                </Box>
              )}
              {enableYSQLAuthValue && !primaryCluster?.userIntent?.enableYSQLAuth && (
                <>
                  <Box flex={1} mt={2} width="300px">
                    <YBPasswordField
                      name={'ysqlPassword'}
                      rules={{
                        required: enableYSQLAuthValue
                          ? (t('universeForm.validation.required', {
                              field: t('universeForm.securityConfig.authSettings.ysqlAuthPassword')
                            }) as string)
                          : '',
                        pattern: {
                          value: PASSWORD_REGEX,
                          message: t('universeForm.validation.passwordStrength')
                        }
                      }}
                      control={control}
                      fullWidth
                      inputProps={{
                        autoComplete: 'new-password',
                        'data-testid': 'YSQLField-PasswordLabelInput'
                      }}
                      placeholder={t('universeForm.securityConfig.placeholder.enterYSQLPassword')}
                    />
                  </Box>
                  <Box flex={1} mt={2} mb={2} width="300px">
                    <YBPasswordField
                      name={'ysqlConfirmPassword'}
                      control={control}
                      rules={{
                        validate: {
                          passwordMatch: (value) =>
                            (enableYSQLAuthValue && value === ysqlPasswordValue) ||
                            (t('universeForm.validation.confirmPassword') as string)
                        },
                        deps: ['ysqlPassword', 'enableYSQLAuth']
                      }}
                      fullWidth
                      inputProps={{
                        autoComplete: 'new-password',
                        'data-testid': 'YSQLField-ConfirmPasswordInput'
                      }}
                      placeholder={t('universeForm.securityConfig.placeholder.confirmYSQLPassword')}
                    />
                  </Box>
                </>
              )}
              {enableYSQLAuthValue && primaryCluster?.userIntent?.enableYSQLAuth && (
                <>
                  <Box mt={2}>
                    <Divider />
                  </Box>
                  <Box mt={2} display="flex" flexDirection={'column'}>
                    <Typography variant="h6">
                      {t('universeActions.editYSQLSettings.YSQLPwdLabel')}
                    </Typography>
                    <Box
                      mt={1}
                      display={'flex'}
                      flexDirection={'row'}
                      className={classes.rotatePwdContainer}
                    >
                      <YBCheckboxField
                        name={'rotateYSQLPassword'}
                        label={t('universeActions.editYSQLSettings.rotatePwdLabel')}
                        control={control}
                        inputProps={{
                          'data-testid': 'RotateYSQLPassword-Checkbox'
                        }}
                      />
                    </Box>
                    {rotateYSQLPasswordValue && (
                      <>
                        <Box flex={1} mt={2} width="300px">
                          <YBPasswordField
                            rules={{
                              required: enableYSQLAuthValue
                                ? (t('universeForm.validation.required', {
                                    field: t(
                                      'universeForm.securityConfig.authSettings.ysqlAuthPassword'
                                    )
                                  }) as string)
                                : ''
                            }}
                            name={'ysqlCurrentPassword'}
                            control={control}
                            fullWidth
                            inputProps={{
                              autoComplete: 'new-password',
                              'data-testid': 'YSQLField-PasswordLabelInput'
                            }}
                            placeholder={t('universeActions.editYSQLSettings.currentPwd')}
                          />
                        </Box>
                        <Box flex={1} mt={2} width="300px">
                          <YBPasswordField
                            rules={{
                              required: enableYSQLAuthValue
                                ? (t('universeForm.validation.required', {
                                    field: t(
                                      'universeForm.securityConfig.authSettings.ysqlAuthPassword'
                                    )
                                  }) as string)
                                : '',
                              pattern: {
                                value: PASSWORD_REGEX,
                                message: t('universeForm.validation.passwordStrength')
                              }
                            }}
                            name={'ysqlNewPassword'}
                            control={control}
                            fullWidth
                            inputProps={{
                              autoComplete: 'new-password',
                              'data-testid': 'YSQLField-PasswordLabelInput'
                            }}
                            placeholder={t('universeActions.editYSQLSettings.newPwd')}
                          />
                        </Box>
                        <Box flex={1} mt={2} mb={2} width="300px">
                          <YBPasswordField
                            name={'ysqlConfirmNewPassword'}
                            control={control}
                            rules={{
                              validate: {
                                passwordMatch: (value) =>
                                  (enableYSQLAuthValue && value === ysqlNewPasswordValue) ||
                                  (t('universeForm.validation.confirmPassword') as string)
                              },
                              deps: ['ysqlPassword', 'enableYSQLAuth']
                            }}
                            fullWidth
                            inputProps={{
                              autoComplete: 'new-password',
                              'data-testid': 'YSQLField-ConfirmPasswordInput'
                            }}
                            placeholder={t('universeActions.editYSQLSettings.reEnterNewPwd')}
                          />
                        </Box>
                      </>
                    )}
                  </Box>
                </>
              )}
            </Box>
          )}
        </Box>
      </FormProvider>
    </YBModal>
  );
};